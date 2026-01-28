import matplotlib.pyplot as plt

def _main():
    data = [
        [x / 3 for x in range(20)],
        [x ** 2 / 50 for x in range(20)],
        [x ** 3 / 1000 for x in range(20)],
        # [x ** 0.5 / 0.5 for x in range(20)],
        # [x ** 0.25 / 0.25 for x in range(20)],
        # [round(x / 3) for x in range(20)],
        # [(x - 10) ** 2 / 12 for x in range(20)],
        # [8 - (x - 10) ** 2 / 12 for x in range(20)],
    ]

    color_list = [
      '#df7126' # 0
    , '#37946e' # 1
    , '#FFFFFF' # 2
    , '#76428a' # 3
    , '#ac3232' # 4
    , '#fbf236' # 5
    , '#b0aca0' # 6
    , '#9e0b00' # 7
    , '#000000' # 8
    , '#fbf236' # 9
    , '#2d4bad' # 10
    , '#196583' # 11
    , '#FFFFFF' #12
    , '#076794' #13
    , '#9db2bd' #14
    ]


    plot_type = [
        {'color': color_list[3], 'marker': 'o', 'linestyle': 'solid', 'markerfacecolor': 'none'},
        {'color': color_list[10], 'marker': 'x', 'linestyle': 'solid', 'markerfacecolor': 'none'},
        {'color': color_list[8], 'marker': '^', 'linestyle': 'solid', 'markerfacecolor': 'none'},
        # {'marker': 'o', 'linestyle': 'dashdot', 'markerfacecolor': 'none'},
        # {'marker': '^', 'linestyle': 'solid'},
        # {'marker': '^', 'linestyle': 'dotted'},
        # {'marker': '^', 'linestyle': 'dashed'},
        # {'marker': 6, 'linestyle': 'dashdot'},
    ]
    names = ['NV-HALT', 'NV-HALT-CL', 'SPHT']

    fig, ax = plt.subplots()
    i = 0
    for datax, plot_typex in zip(data, plot_type):
        # plot_typex['label'] = str(plot_typex['marker']) + '   ' + str(plot_typex['linestyle'])
        plot_typex['label'] = names[i]
        ax.plot(datax, **plot_typex)
        i = i+1
    ax.legend()
    plt.savefig("tmp.png")

    label_params = ax.get_legend_handles_labels() 
    figl, axl = plt.subplots()    
    axl.axis(False)
    axl.legend(*label_params, frameon=False, loc="center", ncol=3, bbox_to_anchor=(0.5, 0.5))
    figl.set_size_inches(4, 0.5, forward=True)
    figl.savefig("LABEL_ONLY.png")


if __name__ == '__main__':
    _main()